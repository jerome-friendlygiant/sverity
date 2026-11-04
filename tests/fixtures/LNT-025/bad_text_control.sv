// sverity-args: --top top
// expect-count: LNT-025 WARNING 3
// expect-none: LNT-025 INFO
module top(input logic a,b,c, output logic [31:0] text_out, output logic q);
  always_comb begin
    text_out = "";
    if (a) text_out = "red";
    if (b) text_out = "blue";
    if (c) text_out = "gold";
  end
  always_ff @(posedge text_out[0]) q <= a;
endmodule
