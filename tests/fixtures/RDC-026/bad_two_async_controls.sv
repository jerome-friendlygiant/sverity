// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: else if (!set_n) q <= 1'b1; else q <= d; => else q <= d;
// revert-expect-none: RDC-026 WARNING
// A register that is both asynchronously cleared and set: simultaneous assertion has no defined value.
// expect: RDC-026 WARNING
module top(input logic clka, rst_n, set_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n or negedge set_n) if (!rst_n) q <= 1'b0; else if (!set_n) q <= 1'b1; else q <= d;
endmodule
