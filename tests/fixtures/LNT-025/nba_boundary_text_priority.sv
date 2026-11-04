// sverity-args: --top top
// expect-count: LNT-025 INFO 3
// expect-none: LNT-025 WARNING
module top(input logic clk,a,b,c, output logic [31:0] text_out);
  always_ff @(posedge clk) begin
    text_out <= "";
    if (a) text_out <= "red";
    if (b) text_out <= "blue";
    if (c) text_out <= "gold";
  end
endmodule
