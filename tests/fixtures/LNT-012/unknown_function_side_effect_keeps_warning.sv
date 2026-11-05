// sverity-args: --top top
// Lookalike of good_pure_function_on_temporary: this function also writes module state, so its summary does not clear it.
// expect: LNT-012 WARNING
module top(input logic [3:0] q, output logic [3:0] y);
  logic [3:0] s, seen;
  function automatic logic [3:0] step(logic [3:0] v);
    seen = v;
    return {v[0] ^ v[3], v[3:1]};
  endfunction
  always_comb begin
    s = q;
    s = step(s);
    y = s;
  end
endmodule
