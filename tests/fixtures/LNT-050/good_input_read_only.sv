// sverity-args: --top top
// Near-miss of bad_assign_to_input: the input is only read; the assignment targets the output.
// expect-none: LNT-050
module top(input var logic a, output logic y);
  always_comb y = a;
endmodule
