// sverity-args: --top top
// expect-count: LNT-025 WARNING 1
// expect-none: LNT-025 INFO
module top(input logic a,b,c, output logic [31:0] text_out);
  always_comb begin
    text_out = "";
    if (a) text_out = "red";
    if (b) text_out = "blue";
  end
endmodule
