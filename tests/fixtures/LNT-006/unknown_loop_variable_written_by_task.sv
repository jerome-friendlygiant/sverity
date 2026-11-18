// sverity-args: --top top --option LNT-006.dynamic_index=true
// A task writes the loop variable through an inout argument, so the loop's range is not its bounds: the default
// check proves nothing, and the opt-in reports the select over the variable's whole type.
// expect: LNT-006 WARNING 11
module top(input logic [7:0] d, output logic [3:0] q);
  function automatic void skip(inout int k); k = k + 1; endfunction
  always_comb begin
    q = '0;
    for (int i = 0; i < 4; i++) begin
      skip(i);
      q[i] = d[i];
    end
  end
endmodule
