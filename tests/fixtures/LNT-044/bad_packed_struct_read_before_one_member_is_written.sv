// sverity-args: --top top
// The whole packed struct is read after only member b is written; member a is written afterwards.
// expect: LNT-044 WARNING 8
module top(input logic x, output logic [1:0] y);
  struct packed { logic a; logic b; } s;
  always_comb begin
    s.b = x;
    y = s;
    s.a = ~x;
  end
endmodule
