// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A top-level data input captured directly: no CDC contract exists for it -> unresolved WARNING.
// expect: CDC-029 WARNING
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
