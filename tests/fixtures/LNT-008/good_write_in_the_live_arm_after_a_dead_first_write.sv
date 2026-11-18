// sverity-args: --top top
// The only instance ties en to 1, so the first write (the if arm) is dead but the else arm writes r: r is driven.
// slang lists one driver per procedure at the first write, which here is the dead one.
// expect-none: LNT-008
module child(input logic en, input logic [7:0] a, b, output logic [7:0] q);
  logic [7:0] r;
  always_comb begin
    if (!en) r = a; else r = b;
  end
  assign q = r;
endmodule
module top(input logic [7:0] a, b, output logic [7:0] q);
  child u(.en(1'b1), .a, .b, .q);
endmodule
