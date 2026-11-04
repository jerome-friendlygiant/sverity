// sverity-args: --top top
// expect-count: LNT-025 WARNING 3
// expect-none: LNT-025 INFO
module top(input logic clk, a, b, c, d, output logic q);
  logic [23:0] reset_text;
  always_comb begin
    reset_text = "";
    if (a) reset_text = "one";
    if (b) reset_text = "two";
    if (c) reset_text = "tri";
  end
  always_ff @(posedge clk or negedge reset_text[0])
    if (!reset_text[0]) q <= 0; else q <= d;
endmodule
