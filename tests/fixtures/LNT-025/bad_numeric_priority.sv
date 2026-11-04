// sverity-args: --top top
// expect-count: LNT-025 WARNING 3
// expect-none: LNT-025 INFO
module top(input logic a,b,c, output logic [31:0] text_out);
  always_comb begin
    text_out = 32'd0;
    if (a) text_out = 32'd1;
    if (b) text_out = 32'd2;
    if (c) text_out = 32'd3;
  end
endmodule
