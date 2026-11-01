// sverity-args: --top top
// assignments inside a called task are not followed: completeness cannot be proven (Unresolved), never silently complete
// expect: LNT-013 WARNING
module top(input logic c, a, output logic y);
  task automatic set_y(input logic v); y = v; endtask
  always @(c or a) if (c) set_y(a);
endmodule
