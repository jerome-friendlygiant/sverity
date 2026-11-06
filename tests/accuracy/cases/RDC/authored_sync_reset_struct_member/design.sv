// Original authored reset-domain example.
module top(input logic clk_i, clear_i, input logic [7:0] data_i, output logic [7:0] out_o);
 typedef struct packed {logic [7:0] data; logic flag;} packet_t;
 packet_t q;
 always_ff @(posedge clk_i) begin
   q.data <= clear_i ? 8'b0 : data_i;
   q.flag <= !clear_i;
   out_o <= q.flag ? q.data : 8'b0;
 end
endmodule
