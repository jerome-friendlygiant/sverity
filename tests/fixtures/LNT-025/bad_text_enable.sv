// sverity-args: --top top
// expect-count: LNT-025 WARNING 3
// expect-none: LNT-025 INFO
module top(input logic clk, a, b, c, d, output logic q);
  logic [15:0] select_text;
  always_comb begin
    select_text = "";
    if (a) select_text = "AA";
    if (b) select_text = "BB";
    if (c) select_text = "CC";
  end
  always_ff @(posedge clk) if (|select_text) q <= d;
endmodule
