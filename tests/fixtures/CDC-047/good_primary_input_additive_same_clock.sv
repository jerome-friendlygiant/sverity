// sverity-args: --top top --sdc @DIR@/primary_input_additive_same_clock.sdc
// expect-none: CDC-047
module top(input logic clk, d, output logic [1:0] q);
  logic a1, a2, b1, b2;
  always_ff @(posedge clk) begin a1 <= d; a2 <= a1; b1 <= d; b2 <= b1; end
  assign q = {a2, b2};
endmodule
