#pragma once

#include "grandmp/core/logger.hpp"

namespace grandmp::core {

// Contrato mínimo para adaptadores de cliente; a implementação pertence à plataforma.
class IClientAdapter {
public:
    virtual ~IClientAdapter() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() noexcept = 0;
    virtual ILogSink& logSink() noexcept = 0;
};

}  // namespace grandmp::core
