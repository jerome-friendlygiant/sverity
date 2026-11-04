// sverity-args: --top top
// expect-count: LNT-025 INFO 3
// expect-none: LNT-025 WARNING
// revert: if (b) => else if (b)
// revert: if (c) => else if (c)
// revert-expect-none: LNT-025
module top(input logic a,b,c, output logic [31:0] text_out);
  always_comb begin
    text_out = "";
    if (a) text_out = "red";
    if (b) text_out = "blue";
    if (c) text_out = "gold";
  end
endmodule
