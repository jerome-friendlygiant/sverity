// sverity-args: --top top
// A signed 'z sign-extends, so it drives all eight bits z: a plain assignment extends by the operand's own signedness, and a
// `?:` whose arms are all signed is signed. Both are the tri-state INFO, not a zero-extended bit. Simulators agree
// (iverilog: zzzzzzzz for both).
// expect: LNT-039 INFO 9
// expect: LNT-039 INFO 10
// expect-none: LNT-039 WARNING
module top(input logic s, output wire [7:0] w1, output wire [7:0] w2);
  assign w1 = 4'sbz;
  assign w2 = s ? 2'sbzz : 8'sb00;
endmodule
