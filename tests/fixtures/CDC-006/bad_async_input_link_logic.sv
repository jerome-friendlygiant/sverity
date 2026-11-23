// sverity-args: --top top --sdc @FIXTURES@/CDC-006/input_async.sdc
// expect: CDC-006 ERROR
// expect-confidence: CDC-006 established
module top(input logic clka, clkb, d, output logic q);
  logic s1, s2; logic u; always_ff @(posedge clka) u <= 1'b0;
  always_ff @(posedge clkb) begin s1 <= d; s2 <= ~s1; end
  assign q = s2;
endmodule
