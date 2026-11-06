// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk,input logic src_i,output logic out_o);
 logic event_q=0,first_q=0;
 initial out_o=0;
 always @(negedge sclk) event_q<=event_q|src_i;
 always @(posedge dclk) begin first_q<=event_q;out_o<=first_q;end
endmodule
