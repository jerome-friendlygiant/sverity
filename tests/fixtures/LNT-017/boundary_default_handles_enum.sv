// sverity-args: --top top
// expect: LNT-017 INFO
module top(input logic s, output logic y);
 typedef enum logic {OFF,ON} mode_t;
 always_comb case(mode_t'(s)) OFF: y=0; default: y=1; endcase
endmodule
