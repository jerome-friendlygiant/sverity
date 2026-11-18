// sverity-args: --top top
// An input terminal of a gate or user-defined primitive is driven like a continuous assignment, so an X there is an
// assigned value (WARNING). A gate reads a 'z input as x (IEEE 1800 sec. 28), so a 'z there is WARNING too; only a
// switch's data terminal passes z (INFO). The output terminal is a target and writes no value.
// expect: LNT-039 WARNING 13
// expect: LNT-039 WARNING 14
// expect: LNT-039 WARNING 15
// expect: LNT-039 WARNING 16
// expect-count: LNT-039 WARNING 4
// expect: LNT-039 INFO 17
module top(input logic a, input logic en);
  wire w1, w2, w3, w4, w5;
  buf b1(w1, 1'bx);
  bufif1 b2(w2, 1'bz, en);
  and g1(w3, a, 1'bx);
  udp_buf u1(w4, 1'bx);
  nmos m1(w5, 1'bz, en);
endmodule
primitive udp_buf(output o, input i);
  table 0 : 0; 1 : 1; x : x; endtable
endprimitive
