// sverity-args: --top top
// The function is exported to C, which may call it: cfg is not reported as never driven.
// expect-none: LNT-008
module top(input logic clk, input logic [7:0] d, output logic [7:0] y);
  logic [7:0] cfg;
  export "DPI-C" function set_cfg;
  function void set_cfg(input int x); cfg = x[7:0]; endfunction
  always_ff @(posedge clk) y <= d ^ cfg;
endmodule
