// Authored for the SVerity accuracy set.
(* blackbox *) module clock_selector(input logic a,b,select_i,output wire y); endmodule
module top(input logic sclk,dclk,input logic [1:0] src_i,output logic out_o);
 wire selected_clk;
 clock_selector mux_u(.a(sclk),.b(dclk),.select_i(src_i[1]),.y(selected_clk));
 always_ff @(posedge selected_clk) out_o<=src_i[0];
endmodule
