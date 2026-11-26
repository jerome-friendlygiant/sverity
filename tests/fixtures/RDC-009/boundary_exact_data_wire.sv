// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The reset net is a plain wire from one data input: it has only exact combinational edges and no non-exact edge, so it sits
// exactly at the trigger's edge and RDC-009 does not fire (no logic gates the reset).
// expect-none: RDC-009
module top(input logic clka, en, d, output logic q);
  logic r;
  assign r = en;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
