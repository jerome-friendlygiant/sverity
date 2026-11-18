// sverity-args: --top top
// The interface is not in the design, so slang cannot check modport directions: the run must not look clean.
// expect: LNT-002 ERROR
// expect: LNT-001 FATAL
// expect-coverage: elaboration INCOMPLETE
module top(missing_if.consumer bus, output logic y);
  assign bus.req = 1'b1;
  assign y = bus.ack;
endmodule
