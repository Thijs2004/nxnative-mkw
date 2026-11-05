using Ryujinx.Graphics.Shader.Decoders;
using Ryujinx.Graphics.Shader.IntermediateRepresentation;
using Ryujinx.Graphics.Shader.Translation;
using static Ryujinx.Graphics.Shader.Instructions.InstEmitHelper;
using static Ryujinx.Graphics.Shader.IntermediateRepresentation.OperandHelper;

namespace Ryujinx.Graphics.Shader.Instructions
{
    // NVN2 task and mesh shaders (Mario Kart World) as compute shaders, for the host's mesh draw emulation (nvn_mtl.m mesh_draw). Metal has mesh shaders, but the translator's IR has no
    // mesh stage, so each stage runs as a compute dispatch that writes its outputs into host buffers, and a generated vertex shader (dmsh_nvn2mesh.py) draws them.
    // The stages' window instructions: MLD Rd, [Ra + off] (0x923: the geometry stage's ISBERD) and MST [Ra + off], Rb (0x927); size 73..76, space 76..80, offset 40..64.
    //   MLD space 7: the workgroup ID; space 3: the task's output as the mesh group sees it: [0] the group's index within its task, then the task's payload.
    //   MST space 11 (task): the task's output window: +4 the number of mesh groups it launches, +0x10 the payload.
    //   MST space 15 (mesh): vertex components, tiled as the hardware keeps them (tile × components × 0x80 + component × 0x80 + (vertex % 32) × 4), the per-primitive
    //   components after the vertex tiles; space 8: B32 at +3 the primitive count, U8 at +4 + 3 p + c the local vertex index of corner c of primitive p.
    // Host buffers, compute storage slots 14 (task) and 15 (mesh), each a run of fixed windows:
    //   task window t at t × TaskStride (the task shader's own window, its space 11 stores as they are);
    //   mesh window g at g × MeshStride: space 15 at +0, the primitive count at +MeshPrims, corner index i (= space 8 byte offset) as a word at +MeshPrims + 4 i (a word, not a byte:
    //   the threads of a group write neighbouring corners at once).
    // Mesh group g belongs to task g / 32 (MeshPerTask), as group g % 32; the host dispatches 32 groups per task and fills the task windows itself for a program without a task
    // stage. Groups past their task's count still run; the vertex shader draws only the counted ones.
    // The stages' storage buffer tables in c[0] (task +0x610, mesh +0x710, 0x100 apart as the other stages' at 0x110 + 0x100 n) map to the compute table at 0x310 (AMapC0).
    static partial class InstEmit
    {
        public const int MeshTaskStride = 0x4000, MeshStride = 0x4800, MeshPrims = 0x3400, MeshPerTask = 32, MeshSlotTask = 14, MeshSlotMesh = 15;
        private const int ComputeStorageTable = 0x310;

        public static bool AMeshStage(EmitterContext context, out int stage)
        {
            stage = context.TranslatorContext.GpuAccessor.QueryNvn2MeshStage();
            return stage == 6 || stage == 7;
        }

        // c[0] of a task / mesh shader: its storage buffer table → the compute table; null when the offset is not in it
        public static (int Bank, int Offset)? AMeshC0(int stage, int offset)
        {
            int table = stage == 6 ? 0x610 : 0x710;

            return offset >= table && offset < table + 0x100 ? (0, ComputeStorageTable + (offset - table)) : null;
        }

        private static Operand AMeshGroup(EmitterContext context)
        {
            return context.Load(StorageKind.Input, IoVariable.CtaId, null, Const(0));
        }

        // (lo, hi) of host buffer slot + window + index + offset, shaped as Ryujinx's global-to-storage pass traces it: the storage table entry as a direct c[0] operand
        private static (Operand Lo, Operand Hi) AMeshAddress(EmitterContext context, int slot, Operand window, Operand index, int offset)
        {
            int entry = ComputeStorageTable + 16 * slot;
            Operand baseLo = Cbuf(0, entry >> 2);
            Operand rel = index != null ? context.IAdd(window, index) : window;

            if (offset != 0)
            {
                rel = context.IAdd(rel, Const(offset));
            }

            Operand lo = context.IAdd(baseLo, rel);
            Operand carry = context.ConditionalSelect(context.ICompareLessUnsigned(lo, baseLo), Const(1), Const(0));
            Operand hi = context.IAdd(Cbuf(0, (entry >> 2) + 1), carry);

            return (lo, hi);
        }

        public static void AMld(EmitterContext context, int stage)
        {
            AmpereOp x = new(context.CurrOp);
            int space = x.Bits(76, 80), offset = (int)x.SignedField(40, 64);
            LsSize size = (LsSize)x.Bits(73, 76);
            bool noIndex = x.Ra == RegisterConsts.RegisterZeroIndex;
            Operand group = AMeshGroup(context);

            if (space == 7)
            {
                if (!noIndex || offset != 0)
                {
                    throw AFail(context, $"MLD space 7 at {offset:x}");
                }

                ASetGpr(context, x.Rd, group);
                return;
            }

            if (space != 3 || stage != 7)
            {
                throw AFail(context, $"MLD space {space} in a {(stage == 6 ? "task" : "mesh")} shader");
            }

            if (noIndex && offset == 0)
            {
                ASetGpr(context, x.Rd, context.BitwiseAnd(group, Const(MeshPerTask - 1)));
                return;
            }

            Operand window = context.IMultiply(context.ShiftRightU32(group, Const(5)), Const(MeshTaskStride));
            Operand index = noIndex ? null : Register(x.Ra, RegisterType.Gpr);
            int count = GetVectorCount(size);
            Operand[] values = new Operand[count];

            for (int i = 0; i < count; i++)
            {
                (Operand lo, Operand hi) = AMeshAddress(context, MeshSlotTask, window, index, offset + 4 * i);
                values[i] = context.Load(GetStorageKind(size), lo, hi);
            }

            for (int i = 0; i < count; i++)
            {
                ASetGpr(context, x.Rd + i, values[i]);
            }
        }

        public static void AMst(EmitterContext context)
        {
            AmpereOp x = new(context.CurrOp);

            if (AIsbe(context, x, true))   // a native task / mesh function's stage buffer (InstEmitAmpereMem.cs)
            {
                return;
            }

            if (!AMeshStage(context, out int stage))
            {
                throw AFail(context, "MST outside a task or mesh shader");
            }

            int space = x.Bits(76, 80), offset = (int)x.SignedField(40, 64);
            LsSize size = (LsSize)x.Bits(73, 76);
            bool noIndex = x.Ra == RegisterConsts.RegisterZeroIndex;
            Operand index = noIndex ? null : Register(x.Ra, RegisterType.Gpr);
            Operand group = AMeshGroup(context);
            int count = GetVectorCount(size);

            Operand Value(int i) => x.Rb + i >= RegisterConsts.RegisterZeroIndex ? Const(0) : Register(x.Rb + i, RegisterType.Gpr);

            if (stage == 6)
            {
                if (space != 11)
                {
                    throw AFail(context, $"MST space {space} in a task shader");
                }

                Operand window = context.IMultiply(group, Const(MeshTaskStride));

                for (int i = 0; i < count; i++)
                {
                    (Operand lo, Operand hi) = AMeshAddress(context, MeshSlotTask, window, index, offset + 4 * i);
                    context.Store(GetStorageKind(size), lo, hi, Value(i));
                }

                return;
            }

            Operand mwindow = context.IMultiply(group, Const(MeshStride));

            if (space == 15)
            {
                if (size < LsSize.B32)
                {
                    throw AFail(context, $"MST space 15 size {size}");
                }

                for (int i = 0; i < count; i++)
                {
                    (Operand lo, Operand hi) = AMeshAddress(context, MeshSlotMesh, mwindow, index, offset + 4 * i);
                    context.Store(StorageKind.GlobalMemory, lo, hi, Value(i));
                }

                return;
            }

            if (space != 8)
            {
                throw AFail(context, $"MST space {space} in a mesh shader");
            }

            if (size == LsSize.B32)
            {
                if (!noIndex || offset != 3)
                {
                    throw AFail(context, $"MST space 8 B32 at {offset:x}");
                }

                (Operand lo, Operand hi) = AMeshAddress(context, MeshSlotMesh, mwindow, null, MeshPrims);
                context.Store(StorageKind.GlobalMemory, lo, hi, Value(0));
                return;
            }

            if (size != LsSize.U8)
            {
                throw AFail(context, $"MST space 8 size {size}");
            }

            // corner byte i → word MeshPrims + 4 i
            Operand corner = noIndex ? Const(offset * 4) : context.ShiftLeft(context.IAdd(index, Const(offset)), Const(2));
            (Operand clo, Operand chi) = AMeshAddress(context, MeshSlotMesh, mwindow, corner, MeshPrims);
            context.Store(StorageKind.GlobalMemory, clo, chi, context.BitwiseAnd(Value(0), Const(0xff)));
        }

        // A mesh shader's first instructions: a group past its task's count of mesh groups ends at once. The host launches 32 groups a task; on the hardware the others do not
        // exist, and run anyway they read the payload of groups that are not there (Mario Kart World: loops bounded by those values hung the GPU in races)
        public static void AMeshPrologue(EmitterContext context)
        {
            if (!AMeshStage(context, out int stage) || stage != 7)
            {
                return;
            }

            Operand group = AMeshGroup(context);
            Operand window = context.IMultiply(context.ShiftRightU32(group, Const(5)), Const(MeshTaskStride));
            (Operand lo, Operand hi) = AMeshAddress(context, MeshSlotTask, window, null, 4);
            Operand count = context.Load(StorageKind.GlobalMemory, lo, hi);
            Operand lblRun = Label();

            context.BranchIfFalse(lblRun, context.ICompareGreaterOrEqualUnsigned(context.BitwiseAnd(group, Const(MeshPerTask - 1)), count));
            context.Return();
            context.MarkLabel(lblRun);
        }

        private static void ASetGpr(EmitterContext context, int rd, Operand value)
        {
            if (rd < RegisterConsts.RegisterZeroIndex)
            {
                context.Copy(Register(rd, RegisterType.Gpr), value);
            }
        }
    }
}
