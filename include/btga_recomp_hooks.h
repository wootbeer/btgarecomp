#ifndef BTGA_RECOMP_HOOKS_H
#define BTGA_RECOMP_HOOKS_H

// Force-included into every file in RecompiledFuncs/ (see CMakeLists.txt),
// for whatever the [[patches.hook]] text in battletanxga.us.rev0.toml needs
// beyond what N64Recomp's own generated recomp.h already provides (ctx,
// S32/S64/U32/U64, lo/hi). Currently empty: every hook in that config today
// (the guarded-division checks) only uses things recomp.h already declares.
// Add declarations here as new hooks need them.

#endif // BTGA_RECOMP_HOOKS_H
