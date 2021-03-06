/*
 * $Id: postgresql_update_radacct_group_trigger.sql,v 1.3 2007/05/14 22:26:58 nbk Exp $
 *
 * OPTIONAL Postgresql trigger for FreeRADIUS
 *
 * This trigger updates fills in the groupname field (which doesnt come in Accounting packets)
 * by querying the radusergroup table.
 * This makes it easier to do group summary reports, however note that it does add some extra
 * database load to 50% of your SQL accounting queries. If you dont care about group summary
 * reports then you dont need to install this.
 *
 */


CREATE OR REPLACE FUNCTION upd_radgroups() RETURNS trigger AS'

DECLARE
        v_groupname varchar;

BEGIN
        SELECT INTO v_groupname groupname FROM radusergroup WHERE calledstationid = NEW.calledstationid AND username = NEW.username;
        IF FOUND THEN
                UPDATE radacct SET groupname = v_groupname WHERE radacctid = NEW.radacctid;
        END IF;

        RETURN NEW;
END

'LANGUAGE plpgsql;


DROP TRIGGER upd_radgroups ON radacct;

CREATE TRIGGER upd_radgroups AFTER INSERT ON radacct
    FOR EACH ROW EXECUTE PROCEDURE upd_radgroups();


