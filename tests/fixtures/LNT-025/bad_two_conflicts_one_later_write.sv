// sverity-args: --top top
// c overrides a and overrides b: two conflicts to review, so two records (and two fingerprints).
// expect-count: LNT-025 WARNING 3
module top(input logic clk, input logic a, input logic b, input logic c, output logic [1:0] q);
  always @(posedge clk) begin
    if (a) q <= 0; if (b) q <= 1;
    if (c) q <= 2;
  end
endmodule
