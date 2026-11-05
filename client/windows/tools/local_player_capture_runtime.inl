// Shared read-only pre-world capture; host supplies existing admitted seams.
void captureLocalPlayer(bool gameplay) noexcept {
    const auto now=GetTickCount64(),session=localSnapshots.session();
    if (!gameplay || !session || !nativeApi || !nativeContext(nullptr) || localSourceStopped.load()) {
        localSnapshots.invalidate();localKinematics.reset();return;
    }
    if (now>=localCaptureMs && now-localCaptureMs<localSnapshots.captureIntervalMs()) return;
    localCaptureMs=now;
    nf::LocalIdentity before{},after{};
    if (!nativeApi->localIdentity(before)) { localSnapshots.invalidate();localKinematics.reset();return; }
    if (localIdentity && before!=*localIdentity) {
        localSourceStopped.store(true);localSnapshots.invalidate();
        writeLog("failed","local_capture_identity_changed_disconnect_no_retry");return;
    }
    localIdentity=before;
    std::uint32_t matrix=0,currentMatrix=0,weaponType=~0u,target=1,flags=0,car=1;
    std::uint8_t weapon=255,focus=1;std::uint16_t disabled=1,cameraMode=1,padMode=UINT16_MAX;
    std::array<std::byte,0x30> pad{};ps::Position position{},forward{};float view=0;
    // Reads only: all offsets already exercised by Alpha 0.8 lifecycle/replay.
    if (!read(before.ped+0x14,matrix) || !matrix || !read(matrix+0x30,position) || !read(matrix+0x10,forward) ||
        !read(nf::pad0,pad) || !read(nf::pad0+0x10E,disabled) || !read(nf::pad0+0x10A,padMode) ||
        !read(ps::playerIndex,focus) || focus ||
        !read(before.ped+0x718,weapon) || !read(before.ped+0x5A0,weaponType) ||
        !read(before.ped+0x71C,target) || !read(before.ped+0x46C,flags) || !read(before.ped+nf::vehicle,car) ||
        !read(ni::cameraHeading,view) || !read(ni::weaponCameraMode,cameraMode) ||
        !nativeApi->localIdentity(after) || before!=after ||
        !read(before.ped+0x14,currentMatrix) || currentMatrix!=matrix) {
        localSnapshots.invalidate();localKinematics.reset();return;
    }
    grandmp::core::OnFootState state{};
    if (!localKinematics.sample(position,forward,view,now,state)) { localSnapshots.invalidate();return; }
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    // Unsupported context publishes neutral controls, never edits local input.
    if (!disabled && padMode<=3 && foreground==GetCurrentProcessId() && weapon==0 && weaponType==0 && !target &&
        (flags&1) && !(flags&0x04000100) && !car && !cameraMode) (void)np::collectOnFootPad(pad,state.controls);
    const auto revision=localSnapshots.nextRevision();
    // Try-lock publication can fail while the previous copied value is still fresh.
    // Only the fidelity profile retains that value, with its ORIGINAL age/session.
    if (!localSnapshots.publish({session,revision,now,state}) && !localSnapshots.retainFreshOnPublishFailure()) localSnapshots.invalidate();
}
