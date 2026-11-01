// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign r = rst_n & en; => assign r = rst_n;
// revert-expect-none: RDC-009 WARNING
// expect: RDC-009 WARNING
module top(input logic clka, rst_n, en, d, output logic q);
  logic r;
  assign r = rst_n & en;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
