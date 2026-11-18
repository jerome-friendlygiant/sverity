// sverity-args: --top top
// The automatic local is written only when c is set; when it is clear, y reads an undefined value.
// expect: LNT-044 WARNING 8
module top(input logic [3:0] a, input logic c, output logic [3:0] y);
  always_comb begin
    automatic logic [3:0] t;
    if (c) t = a;
    y = t;
  end
endmodule
