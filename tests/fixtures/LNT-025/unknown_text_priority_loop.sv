// sverity-args: --top top
// expect-count: LNT-025 WARNING 3
// expect-none: LNT-025 INFO
module top(input logic a, b, c, input int n, output logic [23:0] text_out, output int sum);
  always_comb begin
    sum = 0;
    for (int i = 0; i < n; ++i) begin
      if (i == 2) continue;
      sum += i;
    end
    text_out = "";
    if (a) text_out = "abc";
    if (b) text_out = "def";
    if (c) text_out = "ghi";
  end
endmodule
