// sverity-args: --top top
// Reduced from OpenTitan prim_trivium: the update function only touches its argument, so the order proof still holds.
// expect-none: LNT-012
module top(input logic [3:0] q, output logic [3:0] y);
  function automatic logic [3:0] step(logic [3:0] v);
    logic [3:0] t;
    t = {v[0] ^ v[3], v[3:1]};
    return t;
  endfunction
  logic [3:0] s;
  always_comb begin
    s = q;
    for (int i = 0; i < 2; i++) s = step(s);
    y = s;
  end
endmodule
