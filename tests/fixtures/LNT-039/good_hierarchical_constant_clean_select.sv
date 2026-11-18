// sverity-args: --top top
// A constant select or member access of a hierarchical constant is judged by the bits it selects: P[1:0] and S.a hold no X.
// expect-none: LNT-039
typedef struct packed { logic [1:0] a; logic [1:0] b; } s_t;
module sub #(parameter logic [3:0] P = 4'bxx01, parameter s_t S = '{a: 2'b01, b: 2'bx0}) (output logic o);
  assign o = 1'b0;
endmodule
module top(output logic [1:0] y, z);
  sub u(.o());
  assign y = u.P[1:0];
  assign z = u.S.a;
endmodule
