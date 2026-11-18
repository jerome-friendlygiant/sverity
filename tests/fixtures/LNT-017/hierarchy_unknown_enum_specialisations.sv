// sverity-args: --top top
// No Unresolved outcome: grouped identical and distinct specialisations.
// expect-count: LNT-017 INFO 1
// expect: LNT-017 INFO
module decoder #(parameter bit FULL=0)(input logic s, output logic y);
 typedef enum logic {OFF,ON} mode_t;
 if (FULL) always_comb case(mode_t'(s)) OFF:y=0; ON:y=1; endcase
 else always_comb case(mode_t'(s)) OFF:y=0; default:y=1; endcase
endmodule
module top(input logic s, output logic [2:0] y);
 decoder a(s,y[0]); decoder b(s,y[1]); decoder #(.FULL(1)) c(s,y[2]);
endmodule
