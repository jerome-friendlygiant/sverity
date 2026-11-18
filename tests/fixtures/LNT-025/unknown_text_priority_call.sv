// sverity-args: --top top --option LNT-025.overrides=warning
// expect-count: LNT-025 WARNING 3
// expect-none: LNT-025 INFO
module top(input logic a, b, c, output logic [31:0] label_out);
  function automatic logic choose(input logic x); return x ^ label_out[0]; endfunction
  always_comb begin
    label_out = "";
    if (choose(a)) label_out = "first";
    if (b) label_out = "next";
    if (c) label_out = "last";
  end
endmodule
