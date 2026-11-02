package main

import "encoding/json"

const gatewayProtocolVersion = "2.3"

// These values are linked into the binary by build-gateway-ubuntu.sh. Keeping
// them in the process image prevents /healthz from reporting a different
// generation after an artifact manifest is replaced on disk.
var (
	buildSourceCommit      = "unknown"
	buildSourceFingerprint = "unknown"
	buildGeneration        = "unknown"
	buildGoVersion         = "unknown"
	buildOwnerProfile      = "unknown"
	buildGOMAXPROCS        = "unknown"
)

type gatewayBuildIdentity struct {
	Generation        string `json:"generation"`
	ProtocolVersion   string `json:"protocol_version"`
	SourceCommit      string `json:"source_commit"`
	SourceFingerprint string `json:"source_fingerprint"`
	GoVersion         string `json:"go_version"`
	OwnerProfile      string `json:"owner_profile"`
	GOMAXPROCS        string `json:"gomaxprocs"`
}

func currentGatewayBuildIdentity() gatewayBuildIdentity {
	return gatewayBuildIdentity{
		Generation:        buildGeneration,
		ProtocolVersion:   gatewayProtocolVersion,
		SourceCommit:      buildSourceCommit,
		SourceFingerprint: buildSourceFingerprint,
		GoVersion:         buildGoVersion,
		OwnerProfile:      buildOwnerProfile,
		GOMAXPROCS:        buildGOMAXPROCS,
	}
}

func gatewayBuildIdentityJSON() []byte {
	payload, err := json.Marshal(currentGatewayBuildIdentity())
	if err != nil {
		return []byte(`{"generation":"unknown","protocol_version":"2.3"}`)
	}
	return payload
}
