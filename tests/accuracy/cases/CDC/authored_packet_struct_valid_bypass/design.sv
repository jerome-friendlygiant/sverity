// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic [8:0] src_i, output logic [7:0] out_o);
 typedef struct packed {logic valid; logic [7:0] payload;} packet_t;
 packet_t tx;
 logic v1=0,v2=0;
 always_ff @(posedge sclk) tx <= packet_t'(src_i);
 always_ff @(posedge dclk) begin
   v1 <= tx.valid;
   v2 <= v1;
   if (tx.valid) out_o <= tx.payload;
 end
endmodule
