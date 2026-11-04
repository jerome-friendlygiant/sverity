// sverity-args: --top top
// expect-count: LNT-025 WARNING 3
// expect-none: LNT-025 INFO
module top(input logic a,b,c, input logic [31:0] data_in, output logic [31:0] text_out);
  always_comb begin
    text_out = "";
    if (a) text_out = "red";
    if (b) text_out = "blue";
    if (c) text_out = data_in;
  end
endmodule
