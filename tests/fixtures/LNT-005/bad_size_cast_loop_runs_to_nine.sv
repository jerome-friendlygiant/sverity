// sverity-args: --top top
// The loop runs to 9: the values 8 and 9 do not fit in 3 bits, so 3'(i) wraps and the unsigned result is not i.
// expect: LNT-005 WARNING 7
module top(input logic [9:0] d, output logic [2:0] last);
  always_comb begin
    last = '0;
    for (int i = 0; i < 10; i++) if (d[i]) last = 3'(i);
  end
endmodule
