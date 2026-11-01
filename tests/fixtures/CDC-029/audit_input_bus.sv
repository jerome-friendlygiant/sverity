// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// Regression from the independent audit: this hazard must never be classified as safe.
// 4-bit asynchronous input bus synchronised bit-by-bit: bits can be captured in different cycles.
module top(input logic clkb, input logic [3:0] async_bus, output logic [3:0] q);
  logic [3:0] s1, s2;
  always_ff @(posedge clkb) begin s1 <= async_bus; s2 <= s1; end
  assign q = s2;
endmodule
