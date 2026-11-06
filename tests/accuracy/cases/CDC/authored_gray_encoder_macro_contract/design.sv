// Authored for the SVerity accuracy set.
(* blackbox *) module pointer_encoder(input logic [3:0] count_i,output wire [3:0] code_o); endmodule
module top(input logic sclk,dclk,input logic src_i,output logic [3:0] out_o);
 logic [3:0] count_q=0,stage_q=0;
 wire [3:0] advertised;
 always_ff @(posedge sclk) if(src_i) count_q<=count_q+1'b1;
 pointer_encoder enc(.count_i(count_q),.code_o(advertised));
 always_ff @(posedge dclk) begin stage_q<=advertised;out_o<=stage_q;end
endmodule
