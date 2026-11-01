// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A primary reset input resets the registers directly: its release timing is unknown to the design -> unresolved.
// expect: RDC-003 WARNING
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, arst_n, d, output logic q);
  always_ff @(posedge clka or negedge arst_n) if (!arst_n) q <= 1'b0; else q <= d;
endmodule
