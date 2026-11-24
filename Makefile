# Convenience targets for scripts/verify.sh. The harness is the source of truth; these
# only forward arguments. See AGENTS.md.

.PHONY: verify verify-quick verify-json verify-stage

verify:
	scripts/verify.sh --full

verify-quick:
	scripts/verify.sh --quick

verify-json:
	scripts/verify.sh --full --json

verify-stage:
	@test -n "$(STAGE)" || { echo "usage: make verify-stage STAGE=<env|lint|build-core|test-core|build-release|test-unit|smoke|security>"; exit 64; }
	scripts/verify.sh --stage $(STAGE)
