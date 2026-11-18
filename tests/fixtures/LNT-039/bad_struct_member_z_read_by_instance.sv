// sverity-args: --top top
// The member s.a is only ever z; s.b has its own driver.
// expect: LNT-039 WARNING 10
module sub(input wire a, input wire [3:0] b, output wire y);
  assign y = a ^ ^b;
endmodule
module top(input wire x, output wire y);
  typedef struct packed { logic a; logic b; } s_t;
  wire s_t s;
  assign s.a = 1'bz;
  assign s.b = x;
  sub u(.a(s.a), .b(4'h0), .y(y));
endmodule
