// sverity-args: --top top --sdc @FIXTURES@/_common/async_input.sdc
// expect-none: CDC-044
module top(input logic clka, clkb, rx, x, output logic q);
  logic s1, s2, s3, e;
  always_ff @(posedge clka) begin s1 <= rx; s2 <= s1; s3 <= s2; end
  assign e = s2 ^ s3; always_ff @(posedge e) q <= x;
endmodule
