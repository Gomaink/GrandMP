// All outcomes in this host are SIMULATED. None prove safety in GTA/Hoodlum.
// No native backend, process memory, sockets, tasks or rendering is exercised.
#include "grandmp/core/remote_pose_presentation.hpp"

#include <cstdio>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace p = grandmp::core::pose_presentation;
constexpr auto yes = p::Evidence::satisfied;
constexpr auto no = p::Evidence::contradicted;
constexpr auto unknown = p::Evidence::unknown;
void check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }

struct Fixture {
    p::Expectation expected{{42, 2, 7, 9}, 100, 3, 5};
    p::Request request{{expected.identity, expected.frame, 4, 6}, {42, 6, 100, 200, 30}};
    p::Point desired{100, 200, 30};
    p::Snapshot snapshot(p::Stage stage) const {
        return {request.token, stage, yes, yes, yes};
    }
    p::Frame begin() const { return {expected, request, snapshot(p::Stage::admission)}; }
    void throughWriter(p::Frame& f, p::Point actual) const {
        f.collision(snapshot(p::Stage::collision), yes);
        f.writer(snapshot(p::Stage::writer), actual, yes);
    }
    void throughLocal(p::Frame& f, p::Point actual) const {
        throughWriter(f, actual);
        f.sectors(snapshot(p::Stage::sectors), yes);
        f.localRw(snapshot(p::Stage::local_rw), actual, yes);
    }
    void throughPropagation(p::Frame& f, p::Point actual) const {
        throughLocal(f, actual);
        f.dirty(snapshot(p::Stage::dirty), yes);
        f.propagation(snapshot(p::Stage::propagation), yes);
    }
    void complete(p::Frame& f, p::Point actual) const {
        throughPropagation(f, actual);
        f.verify(snapshot(p::Stage::verification), actual, yes, yes);
    }
};

void outcome(const p::Frame& f, p::Result result, p::Reason reason) {
    check(f.terminal(), "classification must be terminal");
    check(f.verdict().result == result && f.verdict().reason == reason, "unexpected simulated outcome");
    check(!f.verdict().nativeSafetyProven, "simulation never authenticates Hoodlum");
}

void run(std::string_view which) {
    Fixture x;
    if (which == "positive") {
        auto f = x.begin();
        check(!f.terminal() && f.verdict().result == p::Result::unknown, "admission is not final acceptance");
        x.throughPropagation(f, x.desired);
        check(!f.terminal() && f.verdict().at == p::Stage::verification &&
            f.verdict().result == p::Result::unknown, "propagation alone is insufficient");
        f.verify(x.snapshot(p::Stage::verification), x.desired, yes, yes);
        outcome(f, p::Result::accepted, p::Reason::simulated_postconditions_satisfied);
        check(f.verdict().at == p::Stage::verification && f.verdict().token == x.request.token,
            "correlate final value-only verdict with frame and sample");
    } else if (which == "invalid_pose") {
        for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
            for (int axis = 0; axis < 3; ++axis) {
                auto y = x;
                if (axis == 0) y.request.desired.x = bad;
                if (axis == 1) y.request.desired.y = bad;
                if (axis == 2) y.request.desired.z = bad;
                outcome(y.begin(), p::Result::blocked, p::Reason::invalid_request);
            }
        }
    } else if (which == "request_identity") {
        auto y = x; y.request.desired.id = 43;
        outcome(y.begin(), p::Result::blocked, p::Reason::invalid_request);
        y = x; ++y.request.desired.sequence;
        outcome(y.begin(), p::Result::blocked, p::Reason::invalid_request);
        for (int field = 0; field < 4; ++field) {
            y = x;
            if (field == 0) y.expected.identity.player = 0;
            if (field == 1) y.expected.identity.generation = 0;
            if (field == 2) y.expected.identity.entity = 0;
            if (field == 3) y.expected.identity.owner = 0;
            outcome(y.begin(), p::Result::blocked, p::Reason::invalid_request);
        }
    } else if (which == "stale_pose") {
        x.expected.lastSequence = 7;
        outcome(x.begin(), p::Result::blocked, p::Reason::stale_pose);
        x.expected.lastSequence = 5; x.expected.lastRevision = 4;
        outcome(x.begin(), p::Result::blocked, p::Reason::stale_pose);
        x.expected.lastRevision = 5;
        outcome(x.begin(), p::Result::blocked, p::Reason::stale_pose);
        x.expected.lastRevision = 3; ++x.expected.frame;
        outcome(x.begin(), p::Result::blocked, p::Reason::stale_pose);
    } else if (which == "interpolated_revision") {
        x.expected.lastSequence = x.request.token.sequence;
        auto f = x.begin(); x.complete(f, x.desired);
        outcome(f, p::Result::accepted, p::Reason::simulated_postconditions_satisfied);
        ++x.request.token.revision; x.request.desired.x += 0.25f; x.desired.x += 0.25f;
        x.expected.lastRevision = 4;
        auto second = x.begin(); x.complete(second, x.desired);
        outcome(second, p::Result::accepted, p::Reason::simulated_postconditions_satisfied);
    } else if (which == "generation" || which == "owner" || which == "entity") {
        auto f = x.begin(); x.throughPropagation(f, x.desired);
        auto changed = x.snapshot(p::Stage::verification);
        if (which == "generation") ++changed.token.identity.generation;
        if (which == "owner") ++changed.token.identity.owner;
        if (which == "entity") ++changed.token.identity.entity;
        f.verify(changed, x.desired, yes, yes);
        outcome(f, p::Result::blocked, p::Reason::identity_changed);
        auto y = x; y.expected.identity = changed.token.identity;
        outcome(y.begin(), p::Result::blocked, p::Reason::identity_changed);
    } else if (which == "snapshot") {
        for (int field = 0; field < 4; ++field) {
            auto f = x.begin(); x.throughPropagation(f, x.desired);
            auto old = x.snapshot(p::Stage::verification);
            if (field == 0) --old.token.frame;
            if (field == 1) --old.token.revision;
            if (field == 2) --old.token.sequence;
            if (field == 3) old.observedAt = p::Stage::admission;
            f.verify(old, x.desired, yes, yes);
            outcome(f, p::Result::blocked, p::Reason::stale_observation);
        }
    } else if (which == "stage_identity") {
        for (int step = 0; step < 7; ++step) {
            auto f = x.begin();
            const auto sample = [&](p::Stage stage, int index) {
                auto s = x.snapshot(stage);
                if (index == step) ++s.token.identity.owner;
                return s;
            };
            f.collision(sample(p::Stage::collision, 0), yes);
            f.writer(sample(p::Stage::writer, 1), x.desired, yes);
            f.sectors(sample(p::Stage::sectors, 2), yes);
            f.localRw(sample(p::Stage::local_rw, 3), x.desired, yes);
            f.dirty(sample(p::Stage::dirty, 4), yes);
            f.propagation(sample(p::Stage::propagation, 5), yes);
            f.verify(sample(p::Stage::verification, 6), x.desired, yes, yes);
            outcome(f, p::Result::blocked, p::Reason::identity_changed);
        }
    } else if (which == "context" || which == "lifetime" || which == "removed") {
        auto snapshot = x.snapshot(p::Stage::admission);
        if (which == "context") snapshot.context = no;
        if (which == "lifetime") snapshot.lifetime = no;
        if (which == "removed") snapshot.liveConnectedOwned = no;
        p::Frame f{x.expected, x.request, snapshot};
        outcome(f, p::Result::blocked, which == "context" ? p::Reason::context_denied :
            which == "lifetime" ? p::Reason::lifetime_unavailable : p::Reason::entity_unavailable);
        auto late = x.begin(); x.throughPropagation(late, x.desired);
        snapshot.observedAt = p::Stage::verification;
        late.verify(snapshot, x.desired, yes, yes);
        check(late.verdict().result == p::Result::blocked, "revalidate entity/context/lifetime at completion");
    } else if (which == "collision") {
        auto f = x.begin(); f.collision(x.snapshot(p::Stage::collision), no);
        outcome(f, p::Result::blocked, p::Reason::collision_refused);
        auto pending = x.begin(); pending.collision(x.snapshot(p::Stage::collision), yes);
        check(!pending.terminal() && pending.verdict().result == p::Result::unknown,
            "clear corridor does not mean coherent final pose");
    } else if (which == "writer") {
        const p::Point overwritten{101, 200, 30};
        auto f = x.begin(); x.complete(f, overwritten);
        outcome(f, p::Result::blocked, p::Reason::pose_overwritten);
        auto late = x.begin(); x.throughPropagation(late, x.desired);
        late.verify(x.snapshot(p::Stage::verification), overwritten, yes, yes);
        outcome(late, p::Result::blocked, p::Reason::pose_overwritten);
    } else if (which == "physical") {
        auto f = x.begin(); f.collision(x.snapshot(p::Stage::collision), yes);
        f.writer(x.snapshot(p::Stage::writer), x.desired, no);
        outcome(f, p::Result::blocked, p::Reason::physical_incompatible);
        auto late = x.begin(); x.throughPropagation(late, x.desired);
        late.verify(x.snapshot(p::Stage::verification), x.desired, no, yes);
        outcome(late, p::Result::blocked, p::Reason::physical_incompatible);
    } else if (which == "sectors") {
        auto f = x.begin(); x.throughWriter(f, x.desired);
        f.sectors(x.snapshot(p::Stage::sectors), no);
        outcome(f, p::Result::blocked, p::Reason::sectors_diverged);
    } else if (which == "local_rw") {
        auto f = x.begin(); x.throughWriter(f, x.desired);
        f.sectors(x.snapshot(p::Stage::sectors), yes);
        f.localRw(x.snapshot(p::Stage::local_rw), {100, 201, 30}, yes);
        outcome(f, p::Result::blocked, p::Reason::local_rw_diverged);
        auto sameXyz = x.begin(); x.throughWriter(sameXyz, x.desired);
        sameXyz.sectors(x.snapshot(p::Stage::sectors), yes);
        sameXyz.localRw(x.snapshot(p::Stage::local_rw), x.desired, no);
        outcome(sameXyz, p::Result::blocked, p::Reason::local_rw_diverged);
    } else if (which == "dirty") {
        auto f = x.begin(); x.throughLocal(f, x.desired);
        f.dirty(x.snapshot(p::Stage::dirty), no);
        outcome(f, p::Result::blocked, p::Reason::dirty_missing);
        auto notified = x.begin(); x.throughLocal(notified, x.desired);
        notified.dirty(x.snapshot(p::Stage::dirty), yes);
        check(!notified.terminal() && notified.stage() == p::Stage::propagation &&
            notified.verdict().result == p::Result::unknown, "dirty is not propagated");
        notified.propagation(x.snapshot(p::Stage::propagation), no);
        outcome(notified, p::Result::blocked, p::Reason::propagation_failed);
    } else if (which == "unknown") {
        for (int step = 0; step < 10; ++step) {
            auto initial = x.snapshot(p::Stage::admission);
            if (step == 0) initial.context = unknown;
            if (step == 1) initial.liveConnectedOwned = unknown;
            if (step == 2) initial.lifetime = unknown;
            p::Frame f{x.expected, x.request, initial};
            f.collision(x.snapshot(p::Stage::collision), step == 3 ? unknown : yes);
            f.writer(x.snapshot(p::Stage::writer), x.desired, step == 4 ? unknown : yes);
            f.sectors(x.snapshot(p::Stage::sectors), step == 5 ? unknown : yes);
            f.localRw(x.snapshot(p::Stage::local_rw), x.desired, step == 6 ? unknown : yes);
            f.dirty(x.snapshot(p::Stage::dirty), step == 7 ? unknown : yes);
            f.propagation(x.snapshot(p::Stage::propagation), step == 8 ? unknown : yes);
            f.verify(x.snapshot(p::Stage::verification), x.desired, yes, step == 9 ? unknown : yes);
            outcome(f, p::Result::unknown, p::Reason::unknown_evidence);
            const auto saved = f.verdict(); x.complete(f, x.desired);
            check(f.verdict().result == saved.result && f.verdict().reason == saved.reason &&
                f.verdict().at == saved.at, "unknown terminal frame cannot become accepted");
        }
    } else if (which == "latched") {
        auto f = x.begin(); f.collision(x.snapshot(p::Stage::collision), no);
        const auto before = f.verdict();
        for (unsigned i = 0; i < 100; ++i) x.complete(f, x.desired);
        outcome(f, p::Result::blocked, p::Reason::collision_refused);
        check(f.verdict().at == before.at, "no retry or rehabilitation of a rejected frame");
    } else if (which == "order") {
        auto f = x.begin(); f.dirty(x.snapshot(p::Stage::dirty), yes);
        outcome(f, p::Result::blocked, p::Reason::invalid_order);
        auto duplicate = x.begin(); duplicate.collision(x.snapshot(p::Stage::collision), yes);
        duplicate.collision(x.snapshot(p::Stage::collision), yes);
        outcome(duplicate, p::Result::blocked, p::Reason::invalid_order);
    } else if (which == "postconditions") {
        auto f = x.begin(); x.throughPropagation(f, x.desired);
        f.verify(x.snapshot(p::Stage::verification), x.desired, yes, no);
        outcome(f, p::Result::blocked, p::Reason::postconditions_failed);
    } else if (which == "nonfinite_observation") {
        const float bad = std::numeric_limits<float>::quiet_NaN();
        auto f = x.begin(); f.collision(x.snapshot(p::Stage::collision), yes);
        f.writer(x.snapshot(p::Stage::writer), {bad, 200, 30}, yes);
        outcome(f, p::Result::blocked, p::Reason::invalid_observed_pose);
        auto late = x.begin(); x.throughPropagation(late, x.desired);
        late.verify(x.snapshot(p::Stage::verification), {100, bad, 30}, yes, yes);
        outcome(late, p::Result::blocked, p::Reason::invalid_observed_pose);
    } else if (which == "isolation") {
        static_assert(std::is_trivially_copyable_v<p::Token> && std::is_trivially_copyable_v<p::Verdict>);
        static_assert(std::is_trivially_copyable_v<p::Frame> && sizeof(p::Frame) < 512);
        static_assert(!p::Verdict::nativeSafetyProven);
        auto f = x.begin(); x.complete(f, x.desired);
        auto verdict = f.verdict(); ++verdict.token.identity.entity;
        check(f.verdict().token == x.request.token, "verdict is an independent value copy");
    } else throw std::runtime_error("unknown test case");
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "one deterministic case required");
        run(argv[1]);
        std::puts("PASS: simulated frame only; no native/Hoodlum safety is proven");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL (simulation only): %s\n", e.what());
        return 1;
    }
}
