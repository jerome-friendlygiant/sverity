// sverity-args: --top top
// The region inside a procedure holds only a message and an assertion: nothing synthesis builds is switched off.
// expect-none: LNT-083
module top(input logic a, b, output logic y);
  always_comb begin
    y = a & b;
    // synopsys translate_off
    if (a && !b) $display("a without b");
    assert (y == (a & b));
    // synopsys translate_on
  end
endmodule
