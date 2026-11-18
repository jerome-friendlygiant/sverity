// sverity-args: --top top
// Every target bit is a net: a concatenation of nets, and a struct member of a net. A tri-state 'z into them is the INFO,
// not the WARNING that a non-net target gets.
// expect: LNT-039 INFO 10
// expect: LNT-039 INFO 11
// expect-none: LNT-039 WARNING
module top(output wire w1, output wire w2);
  typedef struct packed { logic f; logic g; } S;
  wire S ws;
  assign {w1, w2} = 2'bzz;
  assign ws.f = 1'bz;
endmodule
