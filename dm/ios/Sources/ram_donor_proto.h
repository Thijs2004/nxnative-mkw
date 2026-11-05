// Messages between the app (ram_donor.m) and its memory donor extension (dm/ios/RamDonor/main.m)
#pragma once
#define DM_DONOR_CHUNK (256ull << 20)   // bytes per xpc_shmem
#define DM_DONOR_OP "op"
#define DM_DONOR_SIZE "size"
#define DM_DONOR_SHMEM "shmem"
#define DM_DONOR_ERROR "error"
#define DM_DONOR_AVAILABLE "available"
#define DM_DONOR_OP_CREATE "create"         // allocate size bytes in the donor and reply with them as an xpc_shmem
#define DM_DONOR_OP_AVAILABLE "available"   // reply with os_proc_available_memory() in the donor
