// sverity-args: --top top
// USE = 0: the only call of g is in a generate branch that is not elaborated, so g is as good as uncalled. Its overrun stays a finding, Unresolved.
// expect: LNT-006 WARNING 7
module top #(parameter bit USE = 0)(input logic [3:0] a, output logic y);
  function automatic logic g(input logic [3:0] v);
    logic r = 0;
    for (int i = 0; i < 5; i++) r ^= v[i];
    return r;
  endfunction
  if (USE) begin : gg assign y = g(a); end else begin : gn assign y = ^a; end
endmodule
