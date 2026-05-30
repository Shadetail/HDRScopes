#include "scope/ScopeFactory.h"
#include "scope/WaveformScope.h"
#include "scope/HistogramScope.h"
#include "scope/VectorScope.h"
#include "scope/CIEScope.h"

std::unique_ptr<IScope> CreateScope(ScopeType type) {
    switch (type) {
    case ScopeType::Waveform:    return std::make_unique<WaveformScope>();
    case ScopeType::Histogram:   return std::make_unique<HistogramScope>();
    case ScopeType::Vectorscope: return std::make_unique<VectorScope>();
    case ScopeType::CIE:         return std::make_unique<CIEScope>();
    case ScopeType::Parade:      return std::make_unique<WaveformScope>(); // (parade later)
    default:                     return std::make_unique<WaveformScope>();
    }
}

const char* ScopeTypeName(ScopeType type) {
    switch (type) {
    case ScopeType::Waveform:    return "Waveform";
    case ScopeType::Parade:      return "Parade";
    case ScopeType::Histogram:   return "Histogram";
    case ScopeType::Vectorscope: return "Vectorscope";
    case ScopeType::CIE:         return "CIE Chromaticity";
    default:                     return "Waveform";
    }
}
