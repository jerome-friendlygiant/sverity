// sverity-args: --top top
// revert: a = 1'b1; => y = 1'b1;
// revert-expect-none: LNT-050
// Assigning to an input port (slang error mapped to LNT-050).
// expect: LNT-050 ERROR
module top(input var logic a, output logic y);
  always_comb begin
    y = a;
    a = 1'b1;
  end
endmodule
