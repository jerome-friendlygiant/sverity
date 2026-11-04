// sverity-args: --top top
// Both NBAs schedule the same converted value, so last-write priority cannot change it.
// expect-none: LNT-025
module top(input logic clk, a, b, output logic [3:0] q);
  always_ff @(posedge clk) begin
    if (a) q <= 4'd2;
    if (b) q <= 2;
  end
endmodule
