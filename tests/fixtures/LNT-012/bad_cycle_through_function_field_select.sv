// sverity-args: --top top
// The lookalike: the function also reads the field it computes, so `size` depends on itself through the call.
// expect: LNT-012 ERROR
module top(input logic [3:0] lo, hi, output logic [3:0] y);
  typedef struct packed { logic [3:0] lo, hi, size; } cfg_t;
  function automatic logic [3:0] size_of(cfg_t c);
    return c.hi - c.lo + c.size;
  endfunction
  cfg_t cfg;
  assign cfg.lo = lo;
  assign cfg.hi = hi;
  assign cfg.size = size_of(cfg);
  assign y = cfg.size;
endmodule
