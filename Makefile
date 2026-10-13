GODOT ?= godot

.PHONY: test run test-verbose test-stage bench bench-stress

test:
	$(GODOT) --headless -s addons/gut/gut_cmdln.gd

run:
	$(GODOT)

test-verbose:
	$(GODOT) --headless -s addons/gut/gut_cmdln.gd -glog=3

test-stage:
	@if [ -z "$(STAGE)" ]; then echo "Usage: make test-stage STAGE=1"; exit 1; fi
	$(GODOT) --headless -s addons/gut/gut_cmdln.gd -gselect=stage$(STAGE)

# Precision benchmark: GDScript strategies vs the mpmath oracle corpus
bench:
	$(GODOT) --headless --path . -s res://scripts/testing/run_precision_bench.gd -- --strategies=beam_f64,beam_big112,ladder,ladder_verify --tag=main

bench-stress:
	$(GODOT) --headless --path . -s res://scripts/testing/run_precision_bench.gd -- --dir=stress --strategies=beam_f64,ladder,ladder_verify --tag=stress
