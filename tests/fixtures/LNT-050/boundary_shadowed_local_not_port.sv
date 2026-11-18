// sverity-args: --top top
// Near-miss of bad_assign_to_input: the assignment targets a block-local variable that shadows the input a; the port itself is never written.
// expect-none: LNT-050
module top(input logic a, output logic y);
  always_comb begin : shadow
    logic a;
    a = 1'b1;
    y = a;
  end
endmodule
