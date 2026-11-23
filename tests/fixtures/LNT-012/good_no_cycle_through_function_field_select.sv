// sverity-args: --top top
// Reduced from OpenTitan i3c_core: the function reads only other fields of the struct it is given. Its straight-line body is
// expanded bit by bit, so `size` does not depend on itself: no loop.
// expect-none: LNT-012
module top(input logic [3:0] lo, hi, output logic [3:0] y);
  typedef struct packed { logic [3:0] lo, hi, size; } cfg_t;
  function automatic logic [3:0] size_of(cfg_t c);
    return c.hi - c.lo;
  endfunction
  cfg_t cfg;
  assign cfg.lo = lo;
  assign cfg.hi = hi;
  assign cfg.size = size_of(cfg);
  assign y = cfg.size;
endmodule
