// sverity-args: --top top
// An output argument is a target, not a value: the call writes its actual, and no X literal is assigned to the formal.
// expect-none: LNT-039
module top(output logic [1:0] y);
  task automatic t(output logic [1:0] o); o = 2'b01; endtask
  always_comb t(y);
endmodule
